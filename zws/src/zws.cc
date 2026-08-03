//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic WebSocket-over-HTTP/1 client example

#include <iostream>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZhttpRuntime.hh>

#include <zlib/Zws.hh>

namespace ZwsClient_ {

struct Options {
  ZuCSpan	ca;
  ZuCSpan	message{"ping"};
  ZuCSpan	protocol;
  ZuCSpan	uri;
  uint32_t	messages = 1;
  uint32_t	timeout = 15;
  uint32_t	handshakeTimeout = 10;
  uint32_t	closeTimeout = 5;
  uint32_t	pingInterval = 0;
  uint32_t	pongTimeout = 5;
  bool		requirePong = false;
  bool		verbose = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>, CLI::Long<"ca">)),         (String)),
  (((message),   (CLI::Opt<'m'>, CLI::Long<"message">)),    (String, "ping")),
  (((protocol),  (CLI::Opt<'p'>, CLI::Long<"protocol">)),   (String)),
  (((messages),  (CLI::Opt<'n'>, CLI::Long<"messages">)),   (UInt32, 1)),
  (((timeout),   (CLI::Opt<'t'>, CLI::Long<"timeout">)),    (UInt32, 15)),
  (((handshakeTimeout),
    (CLI::Long<"handshake-timeout">)),                       (UInt32, 10)),
  (((closeTimeout),
    (CLI::Long<"close-timeout">)),                           (UInt32, 5)),
  (((pingInterval),
    (CLI::Long<"ping-interval">)),                           (UInt32, 0)),
  (((pongTimeout),
    (CLI::Long<"pong-timeout">)),                            (UInt32, 5)),
  (((requirePong),
    (CLI::Long<"require-pong">)),                            (Bool, false)),
  (((verbose),   (CLI::Flag<'v'>, CLI::Long<"verbose">)),   (Bool, false)),
  (((uri),       (CLI::Arg<1>)),                            (String)),
  (((help),      (CLI::Flag<'h'>, CLI::Long<"help">)),      (Bool, false)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zws [OPTION]... ws://HOST[:PORT]/TARGET\n\n"
    "Options:\n"
    "  -c, --ca=PATH             CA path for wss\n"
    "  -m, --message=TEXT        text message to send, default \"ping\"\n"
    "  -p, --protocol=TOKEN      WebSocket subprotocol\n"
    "  -n, --messages=N          messages to receive, default 1\n"
    "  -t, --timeout=N           run timeout in seconds, default 15\n"
    "  --handshake-timeout=N     opening deadline, default 10\n"
    "  --close-timeout=N         close deadline, default 5\n"
    "  --ping-interval=N         idle ping interval, 0 disables\n"
    "  --pong-timeout=N          pong deadline, default 5\n"
    "  --require-pong            wait for a matching solicited pong\n"
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
  Zhttp::Runtime	*runtime = nullptr;
  ZmSemaphore		down;
  ZuCSpan		message;
  unsigned		maxMessages = 1;
  bool			verbose = false;
  bool			failed = false;
  unsigned		received = 0;
  unsigned		pongs = 0;

  template <typename Link>
  void connected(Link &link, const Zhttp::ConnectedInfo &info) {
    if (verbose)
      std::cerr << "connected: transport=" << int(info.transport) <<
	" protocol=" << link.protocol() << '\n';
    link.txStream([this](auto &tx) {
      tx << message;
      tx.flush();
    }, Zws::Opcode::Text);
  }

  template <typename Link>
  int messageStart(Link &, Zws::Opcode::T opcode) {
    if (verbose)
      std::cerr << "message: " << Zws::Opcode{}.name(opcode) << '\n';
    return 1;
  }

  template <typename Link, typename Rx>
  int process(Link &, Rx &rx) {
    return Zhttp::bodyEach(rx, [](ZuBSpan span) {
      std::cout.write(
	reinterpret_cast<const char *>(span.data()), span.length());
    }) ? 1 : -1;
  }

  template <typename Link>
  int messageEnd(Link &link) {
    std::cout << '\n' << std::flush;
    if (++received >= maxMessages && pongs) link.close();
    return 1;
  }

  template <typename Link>
  void pong(Link &link, ZuBSpan) {
    ++pongs;
    if (received >= maxMessages) link.close();
  }

  template <typename Link>
  void closed(Link &, uint16_t code, ZuBSpan) {
    if (verbose) std::cerr << "peer close: " << code << '\n';
  }
  template <typename Link>
  void error(Link &, Zws::Failure::T failure) {
    std::cerr << "WebSocket error: " <<
      Zws::Failure{}.name(failure) << '\n';
    failed = true;
  }
  template <typename Link>
  void connectFailed(Link &, bool transient) {
    std::cerr << "connect failed (transient=" << transient << ")\n";
    failed = true;
    runtime->stop();
  }
  template <typename Link>
  void disconnected(Link &, bool peer) {
    if (verbose) std::cerr << "disconnected (peer=" << peer << ")\n";
    down.post();
    runtime->stop();
  }
};

template <typename Profile>
int run(const Options &options, const Zws::URI &uri)
{
  Zhttp::Runtime runtime;
  if (!runtime.init()) return 1;

  ZiMultiplex mx{mxParams()};
  if (!mx.start()) {
    runtime.final();
    return 1;
  }

  App app{
    &runtime, {}, options.message, options.messages,
    options.verbose, false, 0, unsigned(!options.requirePong)};
  Zws::Client<App, Profile> client{&app};
  typename Zws::Client<App, Profile>::Config config;
  if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{})
    config.caPath(options.ca);
  Zws::Config wsConfig;
  wsConfig.handshakeTimeout = options.handshakeTimeout;
  wsConfig.closeTimeout = options.closeTimeout;
  wsConfig.pingInterval = options.pingInterval;
  wsConfig.pongTimeout = options.pongTimeout;

  bool initialized =
    client.init(Zhttp::HubConfig{&mx, "3", "4"}, config, wsConfig);
  bool started = initialized && client.start();
  if (!started) {
    std::cerr << "client initialization/start failed\n";
    if (initialized) client.final();
    mx.stop();
    runtime.final();
    return 1;
  }

  using Link = typename Zws::Client<App, Profile>::Link;
  ZmRef<Link> link = new Link{&client, uri, options.protocol};
  link->connect();

  bool completed = options.timeout ?
    runtime.wait(options.timeout) : (runtime.wait(), true);
  if (!completed) {
    std::cerr << "timed out\n";
    app.failed = true;
    link->close();
  }
  (void)app.down.timedwait(Zm::now(options.closeTimeout + 1));

  ZmSemaphore stopped;
  bool stopOK = false;
  client.stop([&stopOK, &stopped](bool ok) {
    stopOK = ok;
    stopped.post();
  });
  stopped.wait();
  link = nullptr;
  client.final();
  mx.stop();
  runtime.final();
  return app.failed || !stopOK;
}

} // namespace ZwsClient_

using namespace ZwsClient_;

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
  if (argc < 0 || argc != 2 || !options.uri || !options.messages) usage();

  Zws::URI uri;
  auto error = Zws::URI::parse(uri, options.uri);
  if (!error.ok()) {
    std::cerr << "invalid URI (code=" << int(error.code) <<
      ", offset=" << error.offset << ")\n";
    return 1;
  }

  ZiLog::init("zws");
  ZiLog::level(options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int rc = uri.secure() ?
    run<Zhttp::H1TLS>(options, uri) :
    run<Zhttp::H1TCP>(options, uri);
  ZiLog::stop();
  return rc;
}
