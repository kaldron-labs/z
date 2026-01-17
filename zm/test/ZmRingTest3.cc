//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>

#include <zlib/ZmRing.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTimeInterval.hh>

void usage()
{
  std::cerr <<
    "Usage: ZmRingTest3 [OPTION]...\n"
    "  test read/write ring buffer in shared memory\n\n"
    "Options:\n"
    "  -b BUFSIZE\t- set buffer size to BUFSIZE (default: 8192)\n"
    "  -n COUNT\t- set number of messages to COUNT (default: 1)\n"
    "  -m MSGSIZE\t- set message size to MSGSIZE (default: 128)\n";
  Zm::exit(1);
}

struct Msg {
  uint32_t len;
};

struct Params {
  unsigned	bufsize = 8192;
  unsigned	count = 1;
  unsigned	msgsize = 128;
};

template <typename Ring>
class App : public Params {
public:

  App(Params params_) : Params{ZuMv(params_)} {
    ring.init(ZmRingParams{bufsize});
  }
  ~App() { }

  int main();

private:
  void run();

  void reader();
  void writer();

  Ring		ring;
};

int main(int argc, char **argv)
{
  Params params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') usage();
    switch (argv[i][1]) {
      case 'b':
	if (++i >= argc) usage();
	params.bufsize = ZuBox<unsigned>{argv[i]};
	break;
      case 'n':
	if (++i >= argc) usage();
	params.count = ZuBox<unsigned>{argv[i]};
	break;
      case 'm':
	if (++i >= argc) usage();
	params.msgsize = ZuBox<unsigned>{argv[i]};
	break;
      default:
	usage();
	break;
    }
  }

  constexpr auto SizeAxor = [](const void *ptr) {
    return static_cast<const Msg *>(ptr)->len;
  };
  using Ring = ZmRing<ZmRingSizeAxor<SizeAxor>>;
  return App<Ring>{ZuMv(params)}.main();
}

template <typename Ring>
int App<Ring>::main()
{
  run();
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
    "  msgSize: " << ZuBoxed(msgsize) << '\n';

  {
    ZmThread r, w;

    r = ZmThread{[this]() { reader(); }};
    w = ZmThread{[this]() { writer(); }};
    if (w) w.join();
    {
      Ring writer{ring};
      writer.open(Ring::Write);
      writer.eof();
      writer.close();
    }
    if (r) r.join();
  }

  ring.close();
}

template <typename Ring>
void App<Ring>::reader()
{
  std::cerr << "reader started\n";
  Ring reader{ring};
  if (reader.open(Ring::Read) != Zu::OK) {
    std::cerr << "reader open failed\n";
    return;
  }
  if (reader.attach() != Zu::OK) {
    std::cerr << "reader attach failed\n";
    return;
  }
  for (unsigned j = 0, n = count; j < n; j++) {
    if (const Msg *msg = static_cast<const Msg *>(reader.shift())) {
      reader.shift2(msg->len);
      std::cout << "read " << ZuBoxed(msg->len) << " bytes\n";
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
  }
  reader.detach();
  reader.close();
}

template <typename Ring>
void App<Ring>::writer()
{
  unsigned failed = 0;
  std::cerr << "writer started\n";
  Ring writer{ring};
  if (writer.open(Ring::Write) != Zu::OK) {
    std::cerr << "writer open failed\n";
    return;
  }
  for (unsigned j = 0; j < count; j++) {
    if (void *ptr = writer.push(msgsize)) {
      // puts("push");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
      Msg *msg = new (ptr) Msg{msgsize};
#pragma GCC diagnostic pop
      // fwrite("msg written\n", 1, 12, stderr);
      writer.push2(msgsize);
    } else {
      int k = writer.writeStatus();
      if (k == Zu::EndOfFile) {
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
  }
  {
    ZuCArray<64> s;
    s << "push failed " << ZuBoxed(failed) << " times\n"
      << "ring full " << ZuBoxed(writer.full()) << " times\n";
    std::cerr << s;
  }
  writer.close();
}
