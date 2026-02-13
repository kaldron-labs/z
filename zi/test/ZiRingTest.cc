//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmTrap.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTimeInterval.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiRing.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

void usage_()
{
  std::cerr <<
    "Usage: ZiRingTest [OPTION]... NAME\n"
    "  test read/write ring buffer in shared memory\n\n"
	"\tNAME\t- name of shared memory segment\n\n"
    "Options:\n"
    "  -q\t\t- quiet output (default when test-harnessed)\n"
    "  -r\t\t- read from buffer\n"
    "  -w\t\t- write to buffer (default)\n"
    "  -x\t\t- read and write in same process\n"
    "  -X\t\t- reset buffer (overrides -r -w -x)\n"
    "  -W\t\t- multiple writers (default: single writer)\n"
    "  -R\t\t- multiple readers (default: single reader)\n"
    "  -l N\t\t- loop N times\n"
    "  -b BUFSIZE\t- set buffer size to BUFSIZE (default: 8192)\n"
    "  -n COUNT\t- set number of messages to COUNT (default: 1)\n"
    "  -i INTERVAL\t- set delay between messages in seconds (default: 0)\n"
    "  -L\t\t- low-latency (readers spin indefinitely and do not yield)\n"
    "  -s SPIN\t- set spin count to SPIN (default: 1000)\n"
    "  -t TIMEOUT\t- set blocking TIMEOUT in milliseconds (default: 1)\n"
    "  -S\t\t- slow reader (sleep INTERVAL seconds in between reads)\n"
    "  -c CPUSET\t- bind memory to CPUSET\n";
  Zm::exit(1);
}

struct Msg {
  static constexpr uintptr_t magic() { return 0x8040201080402010ULL; }
  Msg() :
      m_p{reinterpret_cast<uintptr_t>(this)},
      m_q{m_p ^ magic()} { }
  ~Msg() { m_p = 0; }
  bool ok() const { return (m_p ^ m_q) == magic(); }
  uintptr_t m_p;
  uintptr_t m_q;
};

struct Params {
  ZtString<>			name;
  bool				write = true;
  bool				read = false;
  bool				modeSet = false;
  bool				reset = false;
  bool				mw = false;
  bool				mr = false;
  unsigned			bufsize = 8192;
  bool				ll = false;
  unsigned			spin = 1000;
  unsigned			timeout = 1;
  unsigned			loop = 1;
  unsigned			count = 1;
  ZuTime			interval;
  bool				slow = false;
  ZmBitmap			cpuset;
};

template <typename Ring>
class App : public Params {
public:

  App(Params params_) : Params{ZuMv(params_)} {
    ring.init(ZiRingParams{name, bufsize}.
	ll(ll).spin(spin).timeout(timeout).cpuset(cpuset));
  }
  ~App() { }

  bool main();

private:
  bool run();

  void reader();
  void writer();

  template <typename ...Args>
  void fail(Args &&...args) {
    ++m_errors;
    log_(ZuFwd<Args>(args)...);
  }

  Ring				ring;
  ZuTime			start, end;
  ZmTimeInterval<ZmSpinLock>	readTime, writeTime;
  ZmAtomic<unsigned>		m_errors{0};
};

int main(int argc, char **argv)
{
  Params params;

  ZiTestResidue::init("ZiRingTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  verbose = !::getenv("HARNESS_ACTIVE");
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      if (params.name) usage_();
      params.name = argv[i];
      continue;
    }
    if (argv[i][2]) usage_();
    switch (argv[i][1]) {
      case 'q':
	verbose = false;
	break;
      case 'w':
	params.modeSet = true;
	params.write = true;
	params.read = false;
	break;
      case 'r':
	params.modeSet = true;
	params.write = false;
	params.read = true;
	break;
      case 'x':
	params.modeSet = true;
	params.write = true;
	params.read = true;
	break;
      case 'X':
	params.reset = true;
	break;
      case 'W':
	params.mw = true;
	break;
      case 'R':
	params.mr = true;
	break;
      case 'l':
	if (++i >= argc) usage_();
	params.loop = ZuBox<unsigned>{argv[i]};
	break;
      case 'b':
	if (++i >= argc) usage_();
	params.bufsize = ZuBox<unsigned>{argv[i]};
	break;
      case 'n':
	if (++i >= argc) usage_();
	params.count = ZuBox<unsigned>{argv[i]};
	break;
      case 'i':
	if (++i >= argc) usage_();
	params.interval = ZuTime(ZuBox<double>{argv[i]}.val());
	break;
      case 'L':
	params.ll = true;
	break;
      case 's':
	if (++i >= argc) usage_();
	params.spin = ZuBox<unsigned>{argv[i]};
	break;
      case 't':
	if (++i >= argc) usage_();
	params.timeout = ZuBox<unsigned>{argv[i]};
	break;
      case 'S':
	params.slow = true;
	break;
      case 'c':
	if (++i >= argc) usage_();
	params.cpuset = argv[i];
	break;
      default:
	usage_();
	break;
    }
  }

  if (!params.name) {
    params.name = ZiTestResidue::uniqueName("ring");
    if (!params.modeSet) {
      params.write = true;
      params.read = true;
    }
  }
  ZiTestResidue::addShmBase(params.name);

  ZiLog::init("ZiRingTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZuTestMain();
  bool ok = ZuSwitch::dispatch<4>(
      (static_cast<unsigned>(params.mw)<<1) |
       static_cast<unsigned>(params.mr),
      [params = ZuMv(params)](auto i) mutable {
	using Ring =
	  ZiRing<ZmRingT<Msg, ZmRingMW<(i>>1) & 1, ZmRingMR<i & 1>>>>;
	return App<Ring>{ZuMv(params)}.main();
      });
  ZuCheck(ok);

  ZiLog::stop();
  return 0;
}

template <typename Ring>
bool App<Ring>::main()
{
  if (reset) {
    if (ring.open(0) != Zu::OK) {
      fail("open failed: ", name);
      return false;
    }
    if (ring.reset() != Zu::OK) {
      fail("reset failed: ", name);
      ring.close();
      return false;
    }
    ring.close();
    return true;
  }

  for (unsigned i = 0; i < loop; i++) {
    if (!run()) break;
  }
  return m_errors == 0;
}

template <typename Ring>
bool App<Ring>::run()
{
  if (ring.open(0) != Zu::OK) {
    fail("open failed: ", name);
    return false;
  }

  log("address: 0x", ZuBoxPtr(ring.data()).hex(),
      "  ctrlSize: ", ZuBoxed(ring.ctrlSize()),
      "  size: ", ZuBoxed(ring.size()),
      "  msgSize: ", ZuBoxed(sizeof(Msg)));

  {
    ZmThread r, w;

    if (read) r = ZmThread{[this]() { reader(); }};
    if (write) w = ZmThread{[this]() { writer(); }};
    if (w) {
      w.join();
      Ring writer{ring};
      if (writer.open(Ring::Write) != Zu::OK)
	fail("writer open failed while sending eof: ", name);
      else {
	writer.eof();
	writer.close();
      }
    }
    if (r) r.join();
  }

  if (start && end) {
    start = end - start;
    log("total time: ", start.interval(),
	"  avg time: ", ((start.as_decimal() / count) * 1000000), " usec");
  }
  {
    ZuCArray<256> s;
    s << "shift: " << readTime << "\n"
      << "push:  " << writeTime << "\n";
    log(s);
  }

  ring.close();
  return m_errors == 0;
}

template <typename Ring>
void App<Ring>::reader()
{
  log("reader started");
  if (!write) start = Zm::now();
  Ring reader{ring};
  if (reader.open(Ring::Read) != Zu::OK) {
    fail("reader open failed: ", name);
    end = Zm::now();
    return;
  }
  if (reader.attach() != Zu::OK) {
    fail("reader attach failed: ", name);
    end = Zm::now();
    reader.close();
    return;
  }
  for (unsigned j = 0, n = count; j < n; j++) {
    ZuTime readStart = Zm::now();
    if (const Msg *msg = reader.shift()) {
      if (ZuUnlikely(!msg->ok())) {
	fail("reader msg validation failed");
	break;
      }
      reader.shift2();
      ZuTime readEnd = Zm::now();
      readTime.add(readEnd -= readStart);
    } else {
      int k = reader.readStatus();
      if (k == Zu::EndOfFile) {
	log("reader EOF");
      } else if (!k)
	log("ring empty");
      else {
	ZuCArray<80> s;
	s << "readStatus() returned " << ZuBoxed(k) << '\n';
	log(s);
      }
      Zm::sleep(.1);
      --j;
      continue;
    }
    if (slow && !!interval) Zm::sleep(interval);
  }
  end = Zm::now();
  reader.detach();
  reader.close();
}

template <typename Ring>
void App<Ring>::writer()
{
  unsigned failed = 0;
  log("writer started");
  start = Zm::now();
  Ring writer{ring};
  if (writer.open(Ring::Write) != Zu::OK) {
    fail("writer open failed: ", name);
    end = Zm::now();
    return;
  }
  for (unsigned j = 0; j < count; j++) {
    ZuTime writeStart = Zm::now();
    if (void *ptr = writer.push()) {
      new (ptr) Msg{};
      if constexpr (Ring::MW)
	writer.push2(ptr);
      else
	writer.push2();
      ZuTime writeEnd = Zm::now();
      writeTime.add(writeEnd -= writeStart);
    } else {
      int k = writer.writeStatus();
      if (k == Zu::EndOfFile) {
	end = Zm::now();
	log("writer EOF");
	break;
      } else if (k == Zu::NotReady) {
	log("no readers");
      } else if (k >= static_cast<int>(sizeof(Msg)))
	log("writer OK!");
      else {
	log("Ring Full");
	++failed;
      }
      Zm::sleep(.1);
      --j;
      continue;
    }
    if (!!interval) Zm::sleep(interval);
  }
  {
    ZuCArray<64> s;
    s << "push failed " << ZuBoxed(failed) << " times\n"
      << "ring full " << ZuBoxed(writer.full()) << " times\n";
    log(s);
  }
  writer.close();
}
