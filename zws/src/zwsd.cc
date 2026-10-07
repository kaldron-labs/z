//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic WebSocket-over-HTTP/1 echo server example

#include <iostream>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/Zws.hh>

namespace ZwsServer_ {

static ZmSemaphore *trapDone;
static void trapped() { if (trapDone) trapDone->post(); }

struct Options {
  ZuCSpan	address{"0.0.0.0"};
  ZuCSpan	cert;
  ZuCSpan	key;
  ZuCSpan	protocol;
  ZuCSpan	target{"/"};
  uint32_t	port = 9001;
  uint64_t	maxMessage = uint64_t(1)<<30;
  uint64_t	maxQueuedInput = uint64_t(1)<<30;
  uint32_t	handshakeTimeout = 10;
  uint32_t	closeTimeout = 5;
  uint32_t	pingInterval = 0;
  uint32_t	pongTimeout = 5;
  bool		verbose = false;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (address,   (CLI::Opt<'a'>, CLI::Long<"address">),		String),
  (cert,      (CLI::Opt<'c'>, CLI::Long<"cert">),		String),
  (key,       (CLI::Opt<'k'>, CLI::Long<"key">),		String),
  (protocol,  (CLI::Long<"protocol">),				String),
  (target,    (CLI::Long<"target">),				String),
  (port,      (CLI::Opt<'p'>, CLI::Long<"port">),		UInt32),
  (maxMessage,
    (CLI::Long<"max-message">),					UInt64),
  (maxQueuedInput,
    (CLI::Long<"max-queued-input">),				UInt64),
  (handshakeTimeout,
    (CLI::Long<"handshake-timeout">),				UInt32),
  (closeTimeout,
    (CLI::Long<"close-timeout">),				UInt32),
  (pingInterval,
    (CLI::Long<"ping-interval">),				UInt32),
  (pongTimeout,
    (CLI::Long<"pong-timeout">),				UInt32),
  (verbose,   (CLI::Flag<'v'>, CLI::Long<"verbose">),		Bool),
  (help,      (CLI::Flag<'h'>, CLI::Long<"help">),		Bool));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zwsd [OPTION]...\n\n"
    "Options:\n"
    "  -a, --address=IP          listen address, default 0.0.0.0\n"
    "  -p, --port=N              listen port, default 9001\n"
    "  -c, --cert=PATH           TLS certificate; requires --key\n"
    "  -k, --key=PATH            TLS private key; requires --cert\n"
    "  --target=PATH             accepted request target, default /\n"
    "  --protocol=TOKEN          required and selected subprotocol\n"
    "  --max-message=N           maximum message bytes\n"
    "  --max-queued-input=N      maximum queued message/input bytes\n"
    "  --handshake-timeout=N     opening deadline, default 10\n"
    "  --close-timeout=N         close deadline, default 5\n"
    "  --ping-interval=N         idle ping interval, 0 disables\n"
    "  --pong-timeout=N          pong deadline, default 5\n"
    "  -v, --verbose             log connection lifecycle\n"
    "  -h, --help                show help\n" << std::flush;
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

struct App {
  struct LinkState {
    using Message = ZtArray<uint8_t, ZtArrayHeapID<"zwsd.Message">>;

    Message					message;
    Zws::Opcode::T					opcode =
      Zws::Opcode::Binary;
  };

  ZmSemaphore		*done = nullptr;
  ZuCSpan		target;
  ZuCSpan		protocol;
  bool			verbose = false;
  bool			failed = false;

  void listening(const ZiListenInfo &info) {
    std::cerr << "listening: " << info.ip << ':' << info.port << '\n';
  }
  void listening() {
    std::cerr << "listening\n";
  }
  void listenFailed(bool transient) {
    std::cerr << "listen failed (transient=" << transient << ")\n";
    failed = true;
    done->post();
  }

  template <typename Link>
  bool accept(
      Link &, ZuBSpan, ZuBSpan target_, ZuBSpan offered,
      Zws::HandshakeString &selected) {
    if (target_ != target) return false;
    if (!protocol) return true;
    if (!Zws::subprotocol(offered, protocol)) return false;
    selected = protocol;
    return true;
  }

  template <typename Link>
  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    if (verbose)
      std::cerr << "connected: protocol=" << link.protocol() << '\n';
  }

  template <typename Link>
  int messageStart(Link &link, Zws::Opcode::T opcode) {
    auto &state = link.state();
    state.message.length(0);
    state.opcode = opcode;
    return 1;
  }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto &state = link.state();
    return Zhttp::bodyEach(
      rx, [&state](ZuSpan<uint8_t> span) { state.message << span; }) ? 1 : -1;
  }

  template <typename Link>
  int messageEnd(Link &link) {
    auto &state = link.state();
    link.txStream([&state](auto &tx) {
      tx << state.message;
      tx.flush();
    }, state.opcode);
    return 1;
  }

  template <typename Link>
  void closed(Link &, uint16_t code, ZuBSpan) {
    if (verbose) std::cerr << "peer close: " << code << '\n';
  }
  template <typename Link>
  void error(Link &, Zws::Failure::T failure) {
    std::cerr << "WebSocket error: " <<
      Zws::Failure{}.name(failure) << '\n';
  }
  template <typename Link>
  void disconnected(Link &, bool peer) {
    if (verbose) std::cerr << "disconnected (peer=" << peer << ")\n";
  }
};

template <typename Profile>
int run(const Options &options)
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

  App app{&done, options.target, options.protocol, options.verbose};
  Zws::Server<App, Profile> server{
    &app, ZiIP{options.address}, options.port};
  typename Zws::Server<App, Profile>::Config config;
  if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{})
    config.certPath(options.cert).keyPath(options.key);
  Zws::Config wsConfig;
  wsConfig.maxMessage = options.maxMessage;
  wsConfig.maxQueuedInput = options.maxQueuedInput;
  wsConfig.handshakeTimeout = options.handshakeTimeout;
  wsConfig.closeTimeout = options.closeTimeout;
  wsConfig.pingInterval = options.pingInterval;
  wsConfig.pongTimeout = options.pongTimeout;

  bool initialized =
    server.init(Zhttp::HubConfig{&mx, "3", "4"}, config, wsConfig);
  bool started = initialized && server.start();
  if (!started) {
    std::cerr << "server initialization/start failed\n";
    if (initialized) server.final();
    mx.stop();
    trapDone = nullptr;
    return 1;
  }

  done.wait();
  server.stopAccepting();
  ZmSemaphore stopped;
  bool stopOK = false;
  server.stop([&stopOK, &stopped](bool ok) {
    stopOK = ok;
    stopped.post();
  });
  stopped.wait();
  server.final();
  mx.stop();
  trapDone = nullptr;
  return app.failed || !stopOK;
}

} // namespace ZwsServer_

using namespace ZwsServer_;

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
  if (argc < 0 || argc != 1 || !options.port || options.port > 65535 ||
      !options.target || options.target[0] != '/' ||
      (!!options.cert != !!options.key))
    usage();

  ZiLog::init("zwsd");
  ZiLog::level(options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int rc = options.cert ?
    run<Zhttp::H1TLS>(options) :
    run<Zhttp::H1TCP>(options);
  ZiLog::stop();
  return rc;
}
