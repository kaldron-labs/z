//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>

#include <zlib/ZmRing.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTimeInterval.hh>

void usage()
{
  std::cerr <<
    "Usage: zmringbench [OPTION]...\n"
    "  test read/write ring buffer in shared memory\n\n"
    "Options:\n"
    "  -w N\t\t- number of writer threads\n"
    "  -r N\t\t- number of reader threads\n"
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

struct alignas(16) Msg {
  Msg() : m_p{reinterpret_cast<uintptr_t>(this)} { }
  bool ok() const { return m_p == reinterpret_cast<uintptr_t>(this); }
  uintptr_t m_p;
};

ZuAssert(alignof(Msg) == 16);

struct Params {
  unsigned			writers = 1;
  unsigned			readers = 1;
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
    ring.init(ZmRingParams{bufsize}.
	ll(ll).spin(spin).timeout(timeout).cpuset(cpuset));
  }
  ~App() { }

  int main();

private:
  void run();

  void reader(unsigned);
  void writer(unsigned);

  Ring				ring;
  ZuTime			start, end;
  ZmTimeInterval<ZmSpinLock>	readTime, writeTime;
};

int main(int argc, char **argv)
{
  Params params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') usage();
    switch (argv[i][1]) {
      case 'w':
	if (++i >= argc) usage();
	params.writers = ZuBox<unsigned>{argv[i]};
	break;
      case 'r':
	if (++i >= argc) usage();
	params.readers = ZuBox<unsigned>{argv[i]};
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

  return ZuSwitch::dispatch<4>(
    (unsigned(params.writers > 1)<<1) | unsigned(params.readers > 1),
    [params = ZuMv(params)](auto I) mutable {
      using Ring =
	ZmRing<ZmRingT<Msg, ZmRingMW<(I>>1) & 1, ZmRingMR<I & 1>>>>;
      return App<Ring>{ZuMv(params)}.main();
    });
}

template <typename Ring>
int App<Ring>::main()
{
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
    auto r = ZmScratch(ZmThread, readers);
    auto w = ZmScratch(ZmThread, writers);

    for (unsigned i = 0; i < readers; i++)
      new (r.push()) ZmThread{[this, i]() { reader(i); }};
    for (unsigned i = 0; i < writers; i++)
      new (w.push()) ZmThread{[this, i]() { writer(i); }};
    for (unsigned i = 0; i < writers; i++)
      if (w[i]) w[i].join();
    {
      Ring writer{ring};
      writer.open(Ring::Write);
      writer.eof();
      writer.close();
    }
    for (unsigned i = 0; i < readers; i++) {
      if (r[i]) r[i].join();
    }
  }

  start = end - start;

  {
    ZuCArray<80> s;
    s << "total time: " << start.interval()
      << "  avg time: " << (start.as_decimal() / ZuDecimal{count}) << '\n';
    std::cerr << s;
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
void App<Ring>::reader(unsigned i)
{
  std::cerr << "reader started\n";
  Ring reader{ring};
  if (reader.open(Ring::Read) != Zu::OK) {
    std::cerr << "reader open failed\n";
    if (!i) end = Zm::now();
    return;
  }
  if (reader.attach() != Zu::OK) {
    std::cerr << "reader attach failed\n";
    if (!i) end = Zm::now();
    return;
  }
  for (unsigned j = 0, n = count * writers; j < n; j++) {
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
	break;
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
  if (!i) end = Zm::now();
  reader.detach();
  reader.close();
}

template <typename Ring>
void App<Ring>::writer(unsigned i)
{
  unsigned failed = 0;
  std::cerr << "writer started\n";
  if (!i) start = Zm::now();
  Ring writer{ring};
  if (writer.open(Ring::Write) != Zu::OK) {
    std::cerr << "writer open failed\n";
    if (!i) end = Zm::now();
    return;
  }
  for (unsigned j = 0; j < count; j++) {
    ZuTime writeStart = Zm::now();
    if (void *ptr = writer.push()) {
      // puts("push");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
      Msg *msg = new (ptr) Msg{};
#pragma GCC diagnostic pop
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
	if (!i) end = Zm::now();
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
