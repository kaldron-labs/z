//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <stdio.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmThread.hh>

class ZmRing_Breakpoint {
public:
  ZmRing_Breakpoint() : m_oneshot(false), m_enabled(false) { }
  void enable(bool oneshot = true) {
    m_oneshot = oneshot;
    m_enabled = true;
  }
  void disable() { m_enabled = false; }
  void wait() const { m_reached.wait(); }
  void proceed() const { m_proceed.post(); }
  void reached(const char *name) const {
    // std::cout << "\t       " << name << std::flush;
    if (!m_enabled) return;
    if (m_oneshot) m_enabled = false;
    m_reached.post();
    m_proceed.wait();
  }

private:
  bool			m_oneshot;
  mutable bool		m_enabled;
  mutable ZmSemaphore	m_reached;
  mutable ZmSemaphore	m_proceed;
};

#define ZmRing_FUNCTEST
#include <zlib/ZmRing.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiRing.hh>

void fail()
{
  Zm::exit(1);
}
#define ensure(x) ((x) ? void() : check_(x, __LINE__, #x))
#define check(x) check_(x, __LINE__, #x)
void check_(bool ok, unsigned line, const char *exp)
{
  std::cout << (ok ? " OK " : "NOK ")
    << ZuBoxed(line).fmt<ZuFmt::Right<6>>() << ' ' << exp
    << '\n' << std::flush;
  if (!ok) fail();
}

class VMsg {
public:
  VMsg() { }
  VMsg(unsigned length) : m_length{length} { }

  ZuInline unsigned length() const { return m_length; }
  ZuInline void *ptr() { return &this[1]; }
  ZuInline const void *ptr() const { return &this[1]; }

  static unsigned SizeAxor(const void *ptr) {
    return sizeof(VMsg) + static_cast<const VMsg *>(ptr)->length();
  }

  static void push(void *ptr, unsigned param) {
    VMsg *msg = new (ptr) VMsg{param - static_cast<unsigned>(sizeof(VMsg))};
    auto data = static_cast<uint8_t *>(msg->ptr());
    for (unsigned i = 0, n = msg->length(); i < n; i++)
      data[i] = i & 0xff;
  }
  bool verify() const {
    auto data = static_cast<const uint8_t *>(ptr());
    for (unsigned i = 0, n = m_length; i < n; i++)
      if (data[i] != (i & 0xff)) return false;
    return true;
  }

private:
  uint32_t	m_length = 0;
};

class Msg {
public:
  Msg() { }
  Msg(unsigned length) : m_length{length} { }

  ZuInline unsigned length() const { return m_length; }
  ZuInline void *ptr() { return &m_data[0]; }
  ZuInline const void *ptr() const { return &m_data[0]; }

  static void push(void *ptr, unsigned param) {
    Msg *msg = new (ptr) Msg{param};
    auto data = static_cast<uint8_t *>(msg->ptr());
    for (unsigned i = 0; i < 4; i++)
      data[i] = (reinterpret_cast<uintptr_t>(data) + i) & 0xff;
  }
  bool verify() const {
    auto data = static_cast<const uint8_t *>(ptr());
    for (unsigned i = 0; i < 4; i++)
      if (data[i] != ((reinterpret_cast<uintptr_t>(data) + i) & 0xff))
	return false;
    return true;
  }

private:
  uint32_t	m_length = 4;
  char		m_data[4];
};

template <typename Ring, typename Msg>
class Thread;

template <typename Ring, typename Msg>
class App {
  using Thread = ::Thread<Ring, Msg>;

public:
  App() : m_nThreads{0}, m_threads{0} { }
  ~App() { if (m_threads) delete [] m_threads; }

  const Thread *thread(unsigned i) const { return m_threads[i]; }
  Thread *thread(unsigned i) { return m_threads[i]; }

  bool start(unsigned nThreads, ZiRingParams params);
  void stop();

  Ring &ring() { return m_ring; }
  const Ring &ring() const { return m_ring; }

private:
  unsigned	m_nThreads;
  ZmRef<Thread>	*m_threads;
  Ring		m_ring;
};

template <typename Ring, typename Msg> class Work;

template <typename Ring, typename Msg>
class Thread : public ZmObject {
public:
  using App = ::App<Ring, Msg>;
  using Work = ::Work<Ring, Msg>;

  Thread(App *app, unsigned id) :
    m_app{app}, m_id{id}, m_ring{app->ring()} { }
  ~Thread() { }

  void operator ()();

  App *app() const { return m_app; }
  unsigned id() const { return m_id; }
  const Ring &ring() const { return m_ring; }
  Ring &ring() { return m_ring; }

  void start() {
    m_thread = ZmThread{[this_ = ZmMkRef(this)]() { (*this_)(); }};
  }
  int synchronous(Work *work) {
    m_work = work;
    m_pending.post();
    m_completed.wait();
    return m_result;
  }
  void asynchronous(Work *work) {
    m_work = work;
    m_pending.post();
  }
  int result() {
    m_completed.wait();
    return m_result;
  }
  void stop() {
    m_work = 0;
    m_pending.post();
    m_thread.join();
  }

private:
  App			*m_app;
  unsigned		m_id;
  ZmThread		m_thread;
  Ring			m_ring;
  ZmSemaphore		m_pending;
  ZmSemaphore		m_completed;
  ZmRef<Work>		m_work;
  int			m_result = 0;
};

template <typename Ring, typename Msg>
bool App<Ring, Msg>::start(unsigned nThreads, ZiRingParams params)
{
  using namespace Zu::IO;
  m_ring.init(ZuMv(params));
  if (m_ring.open(0) != OK) return false;
  m_ring.reset();
  m_threads = new ZmRef<Thread>[m_nThreads = nThreads];
  for (unsigned i = 0; i < nThreads; i++)
    (m_threads[i] = new Thread(this, i))->start();
  return true;
}

template <typename Ring, typename Msg>
void App<Ring, Msg>::stop()
{
  for (unsigned i = 0; i < m_nThreads; i++) {
    m_threads[i]->stop();
    m_threads[i] = 0;
  }
  delete [] m_threads;
  m_threads = 0;
  m_nThreads = 0;
  m_ring.close();
}

template <typename Ring, typename Msg>
class Work : public ZmObject {
public:
  using Thread = ::Thread<Ring, Msg>;

  enum Insn {
    Open = 0,
    Close,
    Push,
    TryPush,
    Push2,
    EndOfFile,
    Attach,
    Detach,
    Shift,
    Shift2,
    ReadStatus,
    WriteStatus
  };

  template <bool V = Ring::V>
  ZuIfT<V, void *> push(Ring &ring, unsigned param) {
    void *ptr = ring.push(param);
    if (ptr) Msg::push(ptr, param);
    return ptr;
  }
  template <bool V = Ring::V>
  ZuIfT<!V, void *> push(Ring &ring, unsigned param) {
    void *ptr;
    ptr = ring.push();
    if (ZuUnlikely(!ptr)) return nullptr;
    Msg::push(ptr, param);
    while (param >= Ring::MsgSize) {
      param -= Ring::MsgSize;
      push2(ring, ptr, 0);
      ptr = ring.push();
      if (ZuUnlikely(!ptr)) return nullptr;
      Msg::push(ptr, param);
    }
    return ptr;
  }

  template <bool V = Ring::V>
  ZuIfT<V, void *> tryPush(Ring &ring, unsigned param) {
    void *ptr = ring.tryPush(param);
    if (ptr) Msg::push(ptr, param);
    return ptr;
  }
  template <bool V = Ring::V>
  ZuIfT<!V, void *> tryPush(Ring &ring, unsigned param) {
    {
      auto i = ring.writeStatus();
      if (i <= 0 || param > static_cast<unsigned>(i)) return nullptr;
    }
    return push(ring, param);
  }

  template <bool MW = Ring::MW, bool V = Ring::V>
  ZuIfT<!MW && !V> push2(Ring &ring, void *, unsigned) {
    ring.push2();
  }
  template <bool MW = Ring::MW, bool V = Ring::V>
  ZuIfT<MW && !V> push2(Ring &ring, void *ptr, unsigned) {
    ring.push2(ptr);
  }
  template <bool MW = Ring::MW, bool V = Ring::V>
  ZuIfT<!MW && V> push2(Ring &ring, void *, unsigned size) {
    ring.push2(size);
  }
  template <bool MW = Ring::MW, bool V = Ring::V>
  ZuIfT<MW && V> push2(Ring &ring, void *ptr, unsigned size) {
    ring.push2(ptr, size);
  }

  template <bool V = Ring::V>
  ZuIfT<V, int> shift(Ring &ring) {
    void *ptr = ring.shift();
    if (ZuUnlikely(!ptr)) return 0;
    auto msg = static_cast<const Msg *>(ptr);
    ensure(msg->verify());
    return Ring::SizeAxor(ptr);
  }
  template <bool V = Ring::V>
  ZuIfT<!V, int> shift(Ring &ring) {
    void *ptr = ring.shift();
    if (ZuUnlikely(!ptr)) return 0;
    auto msg = static_cast<const Msg *>(ptr);
    ensure(msg->verify());
    unsigned n = msg->length();
    unsigned result = n;
    while (n >= Ring::MsgSize) {
      ring.shift2();
      ptr = ring.shift();
      if (ZuUnlikely(!ptr)) return 0;
      n -= Ring::MsgSize;
      auto msg = static_cast<const Msg *>(ptr);
      ensure(msg->verify());
      ensure(msg->length() == n);
    }
    return result;
  }

  template <bool V = Ring::V>
  ZuIfT<!V> shift2(Ring &ring, unsigned) {
    ring.shift2();
  }
  template <bool V = Ring::V>
  ZuIfT<V> shift2(Ring &ring, unsigned size) {
    ring.shift2(size);
  }

  Work(Insn insn, unsigned param = 0) : m_insn{insn}, m_param{param} { }

  int operator ()(Thread *);

private:
  Insn		m_insn;
  unsigned	m_param = 0;
};

template <typename Ring, typename Msg>
void Thread<Ring, Msg>::operator ()()
{
  for (;;) {
    m_pending.wait();
    {
      ZmRef<Work> work = m_work;
      if (!work) return;
      m_work = 0;
      m_result = (*work)(this);
    }
    m_completed.post();
  }
}

template <typename Ring, typename Msg>
int Work<Ring, Msg>::operator()(Thread *thread) {
  Ring &ring = thread->ring();
  int result = 0;
  using VoidPtr = void *;
  auto &ptr = ZmTLS<VoidPtr, &Work::operator()>();

  switch (m_insn) {
    case Open:
      result = ring.open(m_param);
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " open(): " << result << '\n' << std::flush;
      break;
    case Close:
      ring.close();
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " close()\n" << std::flush;
      break;
    case Push:
      if (ptr = push(ring, m_param))
	result = m_param;
      else
	result = 0;
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " push(): " << result << '\n' << std::flush;
      break;
    case TryPush:
      if (ptr = tryPush(ring, m_param))
	result = m_param;
      else
	result = 0;
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " tryPush(): " << result << '\n' << std::flush;
      break;
    case Push2:
      push2(ring, ptr, m_param);
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " push2()\n" << std::flush;
      break;
    case EndOfFile:
      ring.eof();
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " eof()\n" << std::flush;
      break;
    case Attach:
      result = ring.attach();
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " attach(): " << result << '\n' << std::flush;
      break;
    case Detach:
      ring.detach();
      result = Zi::OK;
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " detach(): " << result << '\n' << std::flush;
      break;
    case Shift:
      result = shift(ring);
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " shift(): " << result << '\n' << std::flush;
      ;
      break;
    case Shift2:
      shift2(ring, m_param);
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " shift2()\n" << std::flush;
      break;
    case ReadStatus:
      result = ring.readStatus();
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " readStatus(): " << result << '\n' << std::flush;
      break;
    case WriteStatus:
      result = ring.writeStatus();
      std::cout << "\t" << ZuBoxed(thread->id()).template fmt<ZuFmt::Right<6>>()
		<< " writeStatus(): " << result << '\n' << std::flush;
      break;
  }

  return result;
}

template <bool MW, bool V>
struct RingT;

template <> struct RingT<false, false> {
  ZuDerive(T, (ZiRing<ZmRingT<Msg>>));
};
template <> struct RingT<false, true> {
  ZuDerive(T, (ZiRing<ZmRingSizeAxor<VMsg::SizeAxor>>));
};
template <> struct RingT<true, false> {
  ZuDerive(T, (ZiRing<ZmRingT<Msg, ZmRingMW<true>>>));
};
template <> struct RingT<true, true> {
  ZuDerive(T, (ZiRing<ZmRingSizeAxor<VMsg::SizeAxor, ZmRingMW<true>>>));
};

template <bool MW, bool V>
struct Test {
  using Ring = typename ::RingT<MW, V>::T;
  using Msg = ZuIf<V, VMsg, ::Msg>;
  using App = ::App<Ring, Msg>;
  using Work = ::Work<Ring, Msg>;

  static App *app() {
    return ZmSingleton<App>::instance();
  }

  static int synchronous(int tid, Work *work) {
    return app()->thread(tid)->synchronous(work);
  }

  static void asynchronous_(int tid, Work *work, ZmRing_Breakpoint &bp) {
    bp.enable();
    app()->thread(tid)->asynchronous(work);
    bp.wait();
  }

#define asynchronous(tid, work, bp) \
  asynchronous_(tid, work, app()->thread(tid)->ring().bp_##bp)

  static int result(int tid) {
    return app()->thread(tid)->result();
  }

#define proceed(tid, bp) \
  (app()->thread(tid)->ring().bp_##bp.proceed())

#define Open(p) new Work(Work::Open, p)
#define Close() new Work(Work::Close)
#define Push(p) new Work(Work::Push, p)
#define TryPush(p) new Work(Work::TryPush, p)
#define Push2(p) new Work(Work::Push2, p)
#define EndOfFile() new Work(Work::EndOfFile)
#define Attach() new Work(Work::Attach)
#define Detach() new Work(Work::Detach)
#define Shift() new Work(Work::Shift)
#define Shift2(p) new Work(Work::Shift2, p)
#define ReadStatus() new Work(Work::ReadStatus)
#define WriteStatus() new Work(Work::WriteStatus)

  static bool run(unsigned size) {
    enum { MR = 1 };

    std::cout << "\ntest run" <<
      " MW=" << MW <<
      " MR=" << MR <<
      " V=" << V << '\n';

    if (!app()->start(2 + MR + MW,
	  ZiRingParams{"ZiRingTest2", size})) return false;

    using namespace Zu::IO;

    check(synchronous(0, Open(Ring::Read)) == OK);
    if constexpr (MR) check(synchronous(0 + MR, Open(Ring::Read)) == OK);
    check(synchronous(1 + MR, Open(Ring::Write)) == OK);
    if constexpr (MW) check(synchronous(1 + MR + MW, Open(Ring::Write)) == OK);

    int size1 = app()->ring().size() - Zm::CacheLineSize - 1;
    int size2 = (app()->ring().size() / 2) + 1;

    std::cout << "requested size: " << size
      << " actual size: " << app()->ring().size()
      << " size1: " << size << " size2: " << size2 << '\n' << std::flush;

    // test push with concurrent attach
    if constexpr (MR) check(synchronous(0, Attach()) == OK);
    asynchronous(0 + MR, Attach(), attach2);
    check(synchronous(1 + MR, Push(size1)) > 0);
    if constexpr (MR) asynchronous(0, Shift(), shift1);
    synchronous(1 + MR, Push2(size1));
    if constexpr (MR) proceed(0, shift1);
    proceed(0 + MR, attach2);
    if constexpr (MR) {
      if constexpr (V)
	check(result(0) == size1);
      else
	size1 = result(0);
    }
    check(result(0 + MR) == OK);
    if constexpr (MR) {
      synchronous(0, Shift2(size1));
    } else {
      check(synchronous(0, Shift()) == size1);
      synchronous(0, Shift2(size1));
    }

    // test push with concurrent attach (2)
    check(synchronous(0, Detach()) == OK);
    asynchronous(0, Attach(), attach3);
    check(synchronous(1 + MR, Push(size1)) > 0);
    synchronous(1 + MR, Push2(size1));
    proceed(0, attach3);
    check(result(0) == OK);
    check(synchronous(0, Shift()) == size1);
    synchronous(0, Shift2(size1));
    if constexpr (MR) {
      check(synchronous(0 + MR, Shift()) == size1);
      synchronous(0 + MR, Shift2(size1));
    }

    // test push with concurrent dual shift
    check(synchronous(1 + MR, Push(size2)) > 0);
    asynchronous(0, Shift(), shift1);
    if constexpr (MR) asynchronous(0 + MR, Shift(), shift1);
    synchronous(1 + MR, Push2(size2));
    proceed(0, shift1);
    if constexpr (MR) proceed(0 + MR, shift1);
    check(result(0) == size2);
    if constexpr (MR) check(result(0 + MR) == size2);
    synchronous(0, Shift2(size2));
    if constexpr (MR) synchronous(0 + MR, Shift2(size2));

    // test push with concurrent detach
    check(synchronous(1 + MR, Push(size1)) > 0); 
    asynchronous(0, Detach(), detach3);
    synchronous(1 + MR, Push2(size1));
    if constexpr (MR) {
      check(synchronous(0 + MR, Shift()) == size1);
      synchronous(0 + MR, Shift2(size1));
    }
    proceed(0, detach3);
    check(result(0) == OK);
    if constexpr (MR) {
      check(app()->thread(1 + MR)->ring().length() == 0);
      check(synchronous(1, Detach()) == OK);
    } else {
      check(synchronous(0, Attach()) == OK);
      check(synchronous(0, Shift()) == size1);
      synchronous(0, Shift2(size1));
      check(synchronous(0, Detach()) == OK);
    }

    // test overflow with concurrent detach
    check(synchronous(0, Attach()) == OK);
    if constexpr (MR) check(synchronous(0 + MR, Attach()) == OK);
    check(synchronous(1 + MR, Push(size2)) > 0);
    synchronous(1 + MR, Push2(size2));
    check(synchronous(1 + MR, TryPush(size2)) == 0);
    if constexpr (MR) {
      check(synchronous(0 + MR, Shift()) == size2);
      synchronous(0 + MR, Shift2(size2));
    }
    asynchronous(0, Detach(), detach1);
    if constexpr (MR) check(synchronous(0 + MR, ReadStatus()) == 0);
    proceed(0, detach1);
    check(result(0) == OK);
    if constexpr (MR) check(synchronous(0 + MR, Detach()) == OK);

    synchronous(0, Close());
    if constexpr (MR) synchronous(0 + MR, Close());
    synchronous(1 + MR, Close());
    if constexpr (MW) synchronous(1 + MR + MW, Close());

    app()->stop();

    return true;
  }
};

void usage()
{
  std::cerr <<
    "Usage: ZiRingTest2 [SIZE]\n"
    "\tSIZE - optional requested size of ring buffer\n"
    << std::flush;
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  int size = 8192;

  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    size = atoi(argv[1]);
    if (size <= 0) usage();
  }

  ZiLog::init("ZiRingTest2");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  if (!ZuUnroll::all<4>(true, [size](auto i, bool b) {
    return b ? (b && Test<(i>>1) & 1, i & 1>::run(size)) : false;
  })) return 1;

  return 0;
}
