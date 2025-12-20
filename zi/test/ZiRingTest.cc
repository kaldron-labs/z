//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>

#include <zlib/ZmThread.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTimeInterval.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZiRing.hh>

void usage()
{
  std::cerr <<
    "Usage: ZiRingTest [OPTION]... NAME\n"
    "  test read/write ring buffer in shared memory\n\n"
	"\tNAME\t- name of shared memory segment\n\n"
    "Options:\n"
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

  int main();

private:
  void run();

  void reader();
  void writer();

  Ring				ring;
  ZuTime			start, end;
  ZmTimeInterval<ZmSpinLock>	readTime, writeTime;
};

int main(int argc, char **argv)
{
  Params params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      if (params.name) usage();
      params.name = argv[i];
      continue;
    }
    switch (argv[i][1]) {
      case 'w':
	params.write = true;
	params.read = false;
	break;
      case 'r':
	params.write = false;
	params.read = true;
	break;
      case 'x':
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
	if (++i >= argc) usage();
	params.loop = ZuBox<unsigned>{argv[i]};
	break;
      case 'b':
	if (++i >= argc) usage();
	params.bufsize = ZuBox<unsigned>{argv[i]};
	break;
      case 'n':
	if (++i >= argc) usage();
	params.count = ZuBox<unsigned>{argv[i]};
	break;
      case 'i':
	if (++i >= argc) usage();
	params.interval = ZuTime(ZuBox<double>{argv[i]}.val());
	break;
      case 'L':
	params.ll = true;
	break;
      case 's':
	if (++i >= argc) usage();
	params.spin = ZuBox<unsigned>{argv[i]};
	break;
      case 't':
	if (++i >= argc) usage();
	params.timeout = ZuBox<unsigned>{argv[i]};
	break;
      case 'S':
	params.slow = true;
	break;
      case 'c':
	if (++i >= argc) usage();
	params.cpuset = argv[i];
	break;
      default:
	usage();
	break;
    }
  }

  if (!params.name) usage();

  ZeLog::init("ZiRingTest");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::fileSink(ZeSinkOptions{}.path("&2"))); // log to stderr
  ZeLog::start();

  return ZuSwitch::dispatch<4>(
      (static_cast<unsigned>(params.mw)<<1) |
       static_cast<unsigned>(params.mr),
      [params = ZuMv(params)](auto i) mutable {
	using Ring =
	  ZiRing<ZmRingT<Msg, ZmRingMW<(i>>1) & 1, ZmRingMR<i & 1>>>>;
	return App<Ring>{ZuMv(params)}.main();
      });
}

template <typename Ring>
int App<Ring>::main()
{
  if (reset) {
    if (ring.open(0) != Zu::OK) {
      std::cerr << "open failed\n" << std::flush;
      Zm::exit(1);
    }
    if (ring.reset() != Zu::OK) {
      std::cerr << "reset failed\n" << std::flush;
      Zm::exit(1);
    }
    ring.close();
    return 0;
  }

  for (unsigned i = 0; i < loop; i++) run();
  return 0;
}

template <typename Ring>
void App<Ring>::run()
{
  if (ring.open(0) != Zu::OK) {
    std::cerr << "open failed\n" << std::flush;
    Zm::exit(1);
  }

  std::cerr <<
    "address: 0x" << ZuBoxPtr(ring.data()).hex() <<
    "  ctrlSize: " << ZuBoxed(ring.ctrlSize()) <<
    "  size: " << ZuBoxed(ring.size()) <<
    "  msgSize: " << ZuBoxed(sizeof(Msg)) << '\n';

  {
    ZmThread r, w;

    if (read) r = ZmThread{[this]() { reader(); }};
    if (write) w = ZmThread{[this]() { writer(); }};
    if (w) {
      w.join();
      Ring writer{ring};
      writer.open(Ring::Write);
      writer.eof();
      writer.close();
    }
    if (r) r.join();
  }

  if (start && end) {
    start = end - start;
    std::cerr << (ZuCArray<80>{}
      << "total time: " << start.interval()
      << "  avg time: " << ((start.as_decimal() / count) * 1000000)
      << " usec\n");
  }
  {
    ZuCArray<256> s;
    s << "shift: " << readTime << "\n"
      << "push:  " << writeTime << "\n";
    std::cerr << s;
  }

  ring.close();
}

template <typename Ring>
void App<Ring>::reader()
{
  std::cerr << "reader started\n";
  if (!write) start = Zm::now();
  Ring reader{ring};
  if (reader.open(Ring::Read) != Zu::OK) {
    std::cerr << "reader open failed\n";
    end = Zm::now();
    return;
  }
  if (reader.attach() != Zu::OK) {
    std::cerr << "reader attach failed\n";
    end = Zm::now();
    return;
  }
  for (unsigned j = 0, n = count; j < n; j++) {
    ZuTime readStart = Zm::now();
    if (const Msg *msg = reader.shift()) {
      if (ZuUnlikely(!msg->ok())) {
	std::cerr << "reader msg validation FAILED\n";
	break;
      }
      reader.shift2();
      ZuTime readEnd = Zm::now();
      readTime.add(readEnd -= readStart);
    } else {
      int k = reader.readStatus();
      if (k == Zu::EndOfFile) {
	std::cerr << "reader EOF\n";
      } else if (!k)
	std::cerr << "ring empty\n";
      else {
	ZuCArray<80> s;
	s << "readStatus() returned " << ZuBoxed(k) << '\n';
	std::cerr << s;
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
  std::cerr << "writer started\n";
  start = Zm::now();
  Ring writer{ring};
  if (writer.open(Ring::Write) != Zu::OK) {
    std::cerr << "writer open failed\n";
    end = Zm::now();
    return;
  }
  for (unsigned j = 0; j < count; j++) {
    ZuTime writeStart = Zm::now();
    if (void *ptr = writer.push()) {
      // puts("push");
      // Msg *msg =
      new (ptr) Msg{};
      // fwrite("msg written\n", 1, 12, stderr);
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
	std::cerr << "writer EOF\n";
	break;
      } else if (k == Zu::NotReady) {
	std::cerr << "no readers\n";
      } else if (k >= (int)sizeof(Msg))
	std::cerr << "writer OK!\n";
      else {
	std::cerr << "Ring Full\n";
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
    std::cerr << s;
  }
  writer.close();
}
