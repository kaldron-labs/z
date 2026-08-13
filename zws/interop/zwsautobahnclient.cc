//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// opt-in Autobahn fuzzing-server client

#include <iostream>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/Zws.hh>

namespace ZwsAutobahn_ {

static ZmSemaphore *trapDone;
static void trapped() { if (trapDone) trapDone->post(); }

struct Options {
  ZuCSpan	ca;
  ZuCSpan	agent{"Zws"};
  ZuCSpan	server{"ws://127.0.0.1:9001"};
  uint32_t	timeout = 600;
  bool		verbose = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((ca),       (CLI::Opt<'c'>, CLI::Long<"ca">)),        (String)),
  (((agent),    (CLI::Opt<'a'>, CLI::Long<"agent">)),     (String, "Zws")),
  (((timeout),  (CLI::Opt<'t'>, CLI::Long<"timeout">)),   (UInt32, 600)),
  (((verbose),  (CLI::Flag<'v'>, CLI::Long<"verbose">)),  (Bool, false)),
  (((server),   (CLI::Arg<1>)),                           (String,
							 "ws://127.0.0.1:9001")),
  (((help),     (CLI::Flag<'h'>, CLI::Long<"help">)),     (Bool, false)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zwsautobahnclient [OPTION]... [ws://HOST:PORT]\n\n"
    "  -c, --ca=PATH       CA path for wss\n"
    "  -a, --agent=NAME    report agent name, default Zws\n"
    "  -t, --timeout=N     suite timeout, default 600 seconds\n"
    "  -v, --verbose       report each case\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

template <typename Profile>
struct App {
  struct LinkState {
    ZtArray<uint8_t, ZtArrayHeapID<"Zws.Autobahn.Message">>	message;
    Zws::Opcode::T						opcode =
      Zws::Opcode::Binary;
  };

  using Hub = Zws::Client<App, Profile>;
  using Link = typename Hub::Link;

  enum Phase { Count, Cases, Update, Done };

  ZmSemaphore		*done = nullptr;
  Hub		*hub = nullptr;
  Zws::URI		base;
  ZmRef<Link>		link;
  ZtString<>		agent;
  unsigned		caseCount = 0;
  unsigned		caseNo = 0;
  Phase			phase = Count;
  bool			verbose = false;
  bool			failed = false;

  void begin() { connect_("/getCaseCount"); }

  void connected(Link &, const Zhttp::ConnectedInfo &) { }

  int messageStart(Link &link_, Zws::Opcode::T opcode) {
    auto &state = link_.state();
    state.message.length(0);
    state.opcode = opcode;
    return 1;
  }

  int process(Link &link_, auto &rx) {
    auto &state = link_.state();
    return Zhttp::bodyEach(
      rx, [&state](ZuSpan<uint8_t> span) { state.message << span; }) ? 1 : -1;
  }

  int messageEnd(Link &link_) {
    auto &state = link_.state();
    switch (phase) {
      case Count: {
	ZuBox<unsigned> count{state.message.cspan()};
	caseCount = count;
	if (!caseCount) {
	  failed = true;
	  link_.close();
	}
	break;
      }
      case Cases:
	link_.txStream([&state](auto &tx) {
	  tx << state.message;
	  tx.flush();
	}, state.opcode);
	break;
      case Update:
      case Done:
	break;
    }
    return 1;
  }

  void closed(Link &, uint16_t, ZuBSpan) { }

  void error(Link &, Zws::Failure::T failure) {
    if (phase == Cases) return; // malformed cases intentionally fail
    std::cerr << "Autobahn control connection error: " <<
      Zws::Failure{}.name(failure) << '\n';
    failed = true;
  }

  void connectFailed(Link &, bool transient) {
    std::cerr << "Autobahn connection failed (transient=" <<
      transient << ")\n";
    failed = true;
    done->post();
  }

  void disconnected(Link &, bool) {
    link = nullptr;
    if (failed) {
      phase = Done;
      done->post();
      return;
    }
    switch (phase) {
      case Count:
	phase = Cases;
	caseNo = 1;
	connectCase_();
	break;
      case Cases:
	if (++caseNo <= caseCount)
	  connectCase_();
	else {
	  phase = Update;
	  ZtString<> target;
	  target << "/updateReports?agent=" << agent;
	  connect_(target);
	}
	break;
      case Update:
	phase = Done;
      done->post();
	break;
      case Done:
	break;
    }
  }

private:
  void connectCase_() {
    if (verbose)
      std::cerr << "Autobahn case " << caseNo << '/' << caseCount << '\n';
    ZtString<> target;
    target << "/runCase?case=" << caseNo << "&agent=" << agent;
    connect_(target);
  }

  void connect_(ZuCSpan target) {
    Zws::URI uri = base;
    uri.target = target;
    link = new Link{hub, uri};
    link->connect();
  }
};

template <typename Profile>
int run(const Options &options, const Zws::URI &base)
{
  ZmSemaphore done;
  trapDone = &done;
  ZmTrap::sigintFn(trapped);
  ZmTrap::trap();
  ZiMultiplex mx{mxParams()};
  if (!mx.start()) {
    trapDone = nullptr;
    return 1;
  }

  using App_ = App<Profile>;
  App_ app;
  Zws::Client<App_, Profile> client{&app};
  app.done = &done;
  app.hub = &client;
  app.base = base;
  app.agent = options.agent;
  app.verbose = options.verbose;

  typename Zws::Client<App_, Profile>::Config config;
  if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{})
    config.caPath(options.ca);
  bool initialized =
    client.init(Zhttp::HubConfig{&mx, "3", "4"}, config);
  bool started = initialized && client.start();
  if (!started) {
    if (initialized) client.final();
    mx.stop();
    trapDone = nullptr;
    return 1;
  }

  app.begin();
  if (done.timedwait(Zm::now(options.timeout)) != 0) {
    std::cerr << "Autobahn suite timed out\n";
    app.failed = true;
  }

  ZmSemaphore stopped;
  bool stopOK = false;
  client.stop([&stopOK, &stopped](bool ok) {
    stopOK = ok;
    stopped.post();
  });
  stopped.wait();
  app.link = nullptr;
  client.final();
  mx.stop();
  trapDone = nullptr;
  return app.failed || !stopOK;
}

} // namespace ZwsAutobahn_

using namespace ZwsAutobahn_;

int main(int argc, char **argv)
{
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }
  if (options.help) usage(0);
  if (argc < 0 || argc > 2) usage();

  Zws::URI uri;
  auto error = Zws::URI::parse(uri, options.server);
  if (!error.ok() || (uri.target != "/" && uri.target)) usage();
  uri.target = "/";

  ZiLog::init("zwsautobahnclient");
  ZiLog::level(options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int rc = uri.secure() ?
    run<Zhttp::H1TLS>(options, uri) :
    run<Zhttp::H1TCP>(options, uri);
  ZiLog::stop();
  return rc;
}
