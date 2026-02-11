//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRing.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSpinLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTimeInterval.hh>

using namespace ZuTestUtil;

void usage_()
{
  std::cerr <<
    "Usage: ZmRingTest3 [OPTION]...\n"
    "  test read/write ring buffer in shared memory\n\n"
    "Options:\n"
    "  -q\t\t- suppress diagnostic output\n"
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
  bool run();

  void reader();
  void writer();

  Ring		ring;
};

int main(int argc, char **argv)
{
  Params params;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') usage_();
    if (argv[i][2]) usage_();
    switch (argv[i][1]) {
      case 'q':
	verbose = false;
	break;
      case 'b':
	if (++i >= argc) usage_();
	params.bufsize = ZuBox<unsigned>{argv[i]};
	break;
      case 'n':
	if (++i >= argc) usage_();
	params.count = ZuBox<unsigned>{argv[i]};
	break;
      case 'm':
	if (++i >= argc) usage_();
	params.msgsize = ZuBox<unsigned>{argv[i]};
	break;
      default:
	usage_();
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
  ZuTestMain();

  ZuCheck(run());

  return 0;
}

template <typename Ring>
bool App<Ring>::run()
{
  if (ring.open(0) != Zu::OK) {
    log_("open failed");
    return false;
  }

  log("address: 0x", ZuBoxPtr(ring.data()).hex(),
      "  ctrlSize: ", ZuBoxed(ring.ctrlSize()),
      "  size: ", ZuBoxed(ring.size()),
      "  msgSize: ", ZuBoxed(msgsize));

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

  return true;
}

template <typename Ring>
void App<Ring>::reader()
{
  log("reader started");
  Ring reader{ring};
  if (reader.open(Ring::Read) != Zu::OK) {
    log_("reader open failed");
    return;
  }
  if (reader.attach() != Zu::OK) {
    log_("reader attach failed");
    return;
  }
  for (unsigned j = 0, n = count; j < n; j++) {
    if (const Msg *msg = static_cast<const Msg *>(reader.shift())) {
      reader.shift2(msg->len);
      log("read ", ZuBoxed(msg->len), " bytes");
    } else {
      int k = reader.readStatus();
      if (k == Zu::EndOfFile) {
	log("reader EOF");
	break;
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
  }
  reader.detach();
  reader.close();
}

template <typename Ring>
void App<Ring>::writer()
{
  unsigned failed = 0;
  log("writer started");
  Ring writer{ring};
  if (writer.open(Ring::Write) != Zu::OK) {
    log_("writer open failed");
    return;
  }
  for (unsigned j = 0; j < count; j++) {
    if (void *ptr = writer.push(msgsize)) {
      // log("push");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
      Msg *msg = new (ptr) Msg{msgsize};
#pragma GCC diagnostic pop
      // fwrite("msg written\n", 1, 12, stderr);
      writer.push2(msgsize);
    } else {
      int k = writer.writeStatus();
      if (k == Zu::EndOfFile) {
	log("writer EOF");
	break;
      } else if (k == Zu::NotReady) {
	log("no readers");
      } else if (k >= (int)sizeof(Msg))
	log("writer OK!");
      else {
	log("Ring Full");
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
    log(s);
  }
  writer.close();
}
