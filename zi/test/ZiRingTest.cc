//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
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
    "  -R\t\t- multiple readers (no-op for intrinsically MR ZiRing)\n"
    "  --no-eof\t- do not send/wait for EOF\n"
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
  Msg(uint64_t seq_, uint32_t writer_) :
      m_p{reinterpret_cast<uintptr_t>(this)},
      m_q{m_p ^ magic() ^ seq_ ^ writer_},
      m_seq{seq_}, m_writer{writer_} { }
  ~Msg() { m_p = 0; }
  bool ok() const {
    return (m_p ^ m_q ^ m_seq ^ m_writer) == magic();
  }
  uint64_t seq() const { return m_seq; }
  uint32_t writer() const { return m_writer; }
  uintptr_t m_p;
  uintptr_t m_q;
  uint64_t m_seq;
  uint32_t m_writer;
};

ZmHashKVDerive(WriterSeqs, uint32_t, uint64_t,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"ZiRingTest.WriterSeqs">>));

struct Params {
  ZtString<>		name;
  bool			write = true;
  bool			read = false;
  bool			modeSet = false;
  bool			reset = false;
  bool			mw = false;
  bool			mr = false;
  bool			noEOF = false;
  bool			defaultUnit = false;
  unsigned		bufsize = 8192;
  bool			ll = false;
  unsigned		spin = 1000;
  unsigned		timeout = 1;
  unsigned		loop = 1;
  unsigned		count = 1;
  ZuTime		interval;
  bool			slow = false;
  ZmBitmap		cpuset;
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

  bool validate(WriterSeqs &, const Msg *);

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

void testOpenExisting()
{
  ZuTestScope(testOpenExisting);

  using Ring = ZiRing<ZmRingT<Msg, ZmRingMW<true>>>;
  Zi::Name name = ZiTestResidue::uniqueName("existing");
  ZiTestResidue::addShm(name);
  Ring opener{ZiRingParams{name, 0}};
  ZuCheck(opener.open(Ring::Write) != Zu::OK);
  ZuCheck(opener.closed());

  Ring creator{ZiRingParams{name, 8192}};
  ZuCheck(creator.open(Ring::Write) == Zu::OK);
  unsigned size = creator.size();
  ZuCheck(size >= 8192);
  ZuCheck(creator.reset() == Zu::OK);
  ZuCheck(opener.open(Ring::Write) == Zu::OK);
  ZuCheck(opener.size() == size);
  Ring initial{ZiRingParams{name, 0}.initial(4096)};
  ZuCheck(initial.open(Ring::Write) == Zu::OK);
  ZuCheck(initial.size() == size);
  initial.close();
  opener.close();
  creator.close();

  name = ZiTestResidue::uniqueName("initial");
  ZiTestResidue::addShm(name);
  initial.init(ZiRingParams{name, 0}.initial(6000));
  ZuCheck(initial.open(Ring::Write) == Zu::OK);
  size = initial.size();
  ZuCheck(size >= 6000);
  ZuCheck(initial.reset() == Zu::OK);
  opener.init(ZiRingParams{name, 0});
  ZuCheck(opener.open(Ring::Read) == Zu::OK);
  ZuCheck(opener.size() == size);
  opener.close();
  initial.close();
}

int main(int argc, char **argv)
{
  Params params;
  params.defaultUnit = argc == 1;

  ZiTestResidue::init("ZiRingTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanup);
  ZmTrap::trap();

  ZuTestUtil::parse(1, argv);
  for (int i = 1; i < argc; i++) {
    if (!::strcmp(argv[i], "--no-eof")) {
      params.noEOF = true;
      continue;
    }
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

  bool generatedName = !params.name;
  if (generatedName) {
    params.name = ZiTestResidue::uniqueName("ring");
    if (!params.modeSet) {
      params.write = true;
      params.read = true;
    }
  }
  if (generatedName || !::getenv("HARNESS_ACTIVE"))
    ZiTestResidue::addShm(params.name);

  ZiLog::init("ZiRingTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZuTestMain();
  ZuTestCall(testOpenExisting);
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
  if (defaultUnit) count = (ring.size() / Ring::MsgSize) + 1;

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
      if (!noEOF) {
	Ring writer{ring};
	if (writer.open(Ring::Write) != Zu::OK)
	  fail("writer open failed while sending eof: ", name);
	else {
	  writer.eof();
	  writer.close();
	}
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
  WriterSeqs seqs;
  for (unsigned j = 0, n = count; j < n; j++) {
    ZuTime readStart = Zm::now();
    if (const Msg *msg = reader.shift()) {
      if (ZuUnlikely(!validate(seqs, msg))) {
	break;
      }
      reader.shift2();
      ZuTime readEnd = Zm::now();
      readTime.add(readEnd -= readStart);
    } else {
      int k = reader.readStatus();
      if (k == Zu::EndOfFile) {
	log("reader EOF before message ", ZuBoxed(j));
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
  if (!noEOF && !m_errors) {
    for (;;) {
      if (const Msg *msg = reader.shift()) {
	fail("reader received unexpected message after count: writer ",
	    ZuBoxed(msg->writer()), " sequence ", ZuBoxed(msg->seq()));
	reader.shift2();
	break;
      }
      int k = reader.readStatus();
      if (k == Zu::EndOfFile) {
	log("reader EOF");
	break;
      }
      if (k && k != Zu::NotReady) {
	fail("reader status while waiting for EOF: ", ZuBoxed(k));
	break;
      }
    }
  }
  end = Zm::now();
  reader.detach();
  reader.close();
}

template <typename Ring>
bool App<Ring>::validate(WriterSeqs &seqs, const Msg *msg)
{
  if (ZuUnlikely(!msg->ok())) {
    fail("reader msg corruption: writer ", ZuBoxed(msg->writer()),
	" sequence ", ZuBoxed(msg->seq()));
    return false;
  }
  auto seq = msg->seq();
  if (auto node = seqs.findPtr(msg->writer())) {
    if (ZuUnlikely(node->val() != seq)) {
      fail("reader msg sequence: writer ", ZuBoxed(msg->writer()),
	  " expected ", ZuBoxed(node->val()), " received ", ZuBoxed(seq));
      return false;
    }
    node->val() = seq + 1;
  } else {
    if (ZuUnlikely(!noEOF && seq)) {
      fail("reader initial msg sequence: writer ", ZuBoxed(msg->writer()),
	  " expected 0 received ", ZuBoxed(seq));
      return false;
    }
    seqs.add(msg->writer(), seq + 1);
  }
  return true;
}

template <typename Ring>
void App<Ring>::writer()
{
  unsigned failed = 0;
  uint32_t writerID = uint32_t(Zm::getPID());
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
      new (ptr) Msg{j, writerID};
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
