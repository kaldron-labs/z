//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// REST/HTTP Ping server

#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>

#include "zrestd_proto.hh"

ZtEnumImplNS(Http2Mode);

ZfStruct((Options, CLI),
  (((addr),       (CLI::Long<"addr">)),                       (String, "0.0.0.0")),
  (((port),       (CLI::Long<"port">)),                       (UInt32, 8080)),
  (((cert),       (CLI::Long<"cert">)),                       (String)),
  (((key),        (CLI::Long<"key">)),                        (String)),
  (((keyLog),     (CLI::Long<"key-log">)),                    (String)),
  (((logPath),    (CLI::Long<"log">)),                        (String, "-")),
  (((pidfile),    (CLI::Long<"pidfile">)),                    (String)),
  (((eventFD),    (CLI::Long<"event-fd">)),                   (Int32, -1)),
  (((user),       (CLI::Long<"user">)),                       (String, "test")),
  (((pass),       (CLI::Long<"pass">)),                       (String, "test123")),
  (((jwtSecret),  (CLI::Long<"jwt-secret">)),                 (String,
							 "your_secret_key")),
  (((accessTokenLifetime),
    (CLI::Long<"access-token-lifetime">)),                    (String, "5m")),
  (((refreshTokenLifetime),
    (CLI::Long<"refresh-token-lifetime">)),                   (String, "24h")),
  (((requests),   (CLI::Opt<'n'>, CLI::Long<"requests">)),    (UInt32, 1)),
  (((maxconn),    (CLI::Long<"maxconn">)),                    (UInt32)),
  (((timeout),    (CLI::Long<"timeout">)),                    (UInt32, 30)),
  (((ipv6),       (CLI::Long<"ipv6">)),                       (Bool)),
  (((daemon),     (CLI::Long<"daemon">)),                     (Bool)),
  (((syslog),     (CLI::Long<"syslog">)),                     (Bool)),
  (((noKeepalive), (CLI::Long<"no-keepalive">)),              (Bool)),
  (((noServerID),  (CLI::Long<"no-server-id">)),              (Bool)),
  (((http),       (CLI::Long<"http">)),                       (Bool, true)),
  (((https),      (CLI::Long<"https">)),                      (Bool)),
  (((http3),      (CLI::Long<"http3">)),                      (Bool)),
  (((verbose),    (CLI::Flag<'v'>, CLI::Long<"verbose">)),    (Bool)),
  (((http2),      (Enum<Http2Mode::Map>, CLI::Long<"http2">)),
								 (Int8, Http2Mode::prefer)),
  (((quicHeartbeat),
    (CLI::Long<"quic-heartbeat">)),                           (UInt32)),
#ifdef ZiMultiplex_DEBUG
  (((debug),      (CLI::Long<"debug">)),                      (Bool)),
  (((frag),       (CLI::Long<"frag">)),                       (Bool)),
  (((yield),      (CLI::Long<"yield">)),                      (Bool)),
#endif
#ifdef ZiMultiplex_FILTER
  (((quicRxDrop), (CLI::Long<"quic-rx-drop">)),               (String)),
  (((quicTxDrop), (CLI::Long<"quic-tx-drop">)),               (String)),
#endif
#ifdef Zquic_DEBUG
  (((quicDiag),   (CLI::Long<"quic-diag">)),                  (UInt32)),
#endif
  (((memDiag),    (CLI::Long<"mem-diag">)),                   (UInt32)),
  (((help),       (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool)));

ZmSemaphore done;

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zrestd [OPTION]...\n\n"
    "Options:\n"
    "  --port=N           listen port, default 8080\n"
    "  --addr=IP          listen address, default all interfaces\n"
    "  --ipv6             listen on IPv6 address\n"
    "  --daemon           detach and run in background\n"
    "  --pidfile=PATH     write PID to file\n"
    "  --maxconn=N        maximum concurrent accepted connections\n"
    "  --log=PATH         write log to PATH, '-' for stderr\n"
    "  --syslog           send log to syslog\n"
    "  -n, --requests=N   process N Ping requests then exit, default 1\n"
    "  --no-keepalive     close each connection after its Pong\n"
    "  --no-server-id     omit server identity headers (the default)\n"
    "  --timeout=N        idle connection timeout, default 30, 0 disables\n"
    "  --http             enable HTTP/1.1 over TCP, default\n"
    "  --https            enable HTTP/1.1 or HTTP/2 over TLS\n"
    "  --http2=MODE       HTTP/2 mode: force, prefer, disable\n"
    "  --http3            enable HTTP/3 over QUIC\n"
    "  --cert=PATH        TLS certificate for --https/--http3\n"
    "  --key=PATH         TLS private key for --https/--http3\n"
    "  --key-log=PATH     append HTTP/3 TLS secrets\n"
    "  --quic-heartbeat=N send QUIC PING after N idle seconds, 0 disables\n"
#ifdef ZiMultiplex_DEBUG
    "  --debug            enable ZiMultiplex and HTTP/3 debug logging\n"
    "  --frag             fragment ZiMultiplex I/O in debug builds\n"
    "  --yield            yield in ZiMultiplex in debug builds\n"
#endif
#ifdef ZiMultiplex_FILTER
    "  --quic-rx-drop=N%  randomly drop N% of received QUIC UDP packets\n"
    "  --quic-tx-drop=N%  randomly drop N% of transmitted QUIC UDP packets\n"
#endif
#ifdef Zquic_DEBUG
    "  --quic-diag=N      print HTTP/3 QUIC counters every N seconds\n"
#endif
    "  --mem-diag=N       print memory counters every N seconds\n"
    "  --event-fd=N       emit process-supervision events\n"
    "  --user=USER        accepted username, default test\n"
    "  --pass=PASS        accepted password, default test123\n"
    "  --jwt-secret=TEXT  HS256 secret, default your_secret_key\n"
    "  --access-token-lifetime=Ns|Nm|Nh\n"
    "                     access lifetime, default 5m\n"
    "  --refresh-token-lifetime=Ns|Nm|Nh\n"
    "                     refresh lifetime, default 24h\n"
    "  -v, --verbose      emit stable auth/refresh/pong events\n"
    "  -h, --help         show help\n" << std::flush;
  ::exit(code);
}

#ifdef ZiMultiplex_FILTER
static bool parseDrop(ZuCSpan s, double &drop)
{
  drop = 0.0;
  if (!s) return true;
  if (s.length() < 2 || s[s.length() - 1] != '%') return false;
  ZuBox<double> pct;
  if (pct.scan(s.data(), s.length() - 1) != s.length() - 1) return false;
  drop = pct;
  if (drop < 0.0 || drop > 100.0) return false;
  drop *= .01;
  return true;
}
#endif

static bool loadOptions(Options &options, int argc, char **argv)
{
  bool httpSet = false;
  for (int i = 1; i < argc; ++i)
    if (ZuCSpan{argv[i]} == "--http") httpSet = true;
  int argc_ = ZfCLI::load(options, argc, argv);
  if (options.help) usage(0);
  if (argc_ != 1 || !options.requests || options.port > 65535 ||
      options.http2 < 0 || options.http2 >= Http2Mode::N)
    return false;
  if (!options.user || !options.pass || !options.jwtSecret ||
      !parseDuration(options.accessTokenLifetime, false, options.accessSecs) ||
      !parseDuration(options.refreshTokenLifetime, false, options.refreshSecs) ||
      options.refreshSecs <= options.accessSecs) return false;
  if (!httpSet && (options.https || options.http3)) options.http = false;
  if ((options.https || options.http3) && (!options.cert || !options.key))
    return false;
  if (options.ipv6 && options.addr == "0.0.0.0") options.addr = "::";
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop) ||
      !parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

static void printMemDiag()
{
  ZiLOG(Info, "zrestd", ([](auto &s) {
    s << "Hash Tables:\n" << Ztc::hashCSV();
    s << "Heaps:\n" << Ztc::heapCSV();
  }));
}

static bool prepareProcess(const Options &options)
{
  const char *pidfile = options.pidfile ? options.pidfile.data() : nullptr;
  int rc = ZiDaemon::init(nullptr, nullptr, -1, options.daemon, pidfile);
  if (rc == ZiDaemon::Running)
    ZiLOG(Error, "zrestd", "PID file names a running process");
  else if (rc != ZiDaemon::OK)
    ZiLOG(Error, "zrestd", "daemon initialization failed");
  return rc == ZiDaemon::OK;
}

void App::listening(int transport, unsigned port)
{
  ZiLOG(Info, "zrestd", ([transport, port](auto &s) {
    s << "event=listening transport=";
    switch (transport) {
      case Zhttp::Transport::QUIC: s << "h3"; break;
      case Zhttp::Transport::TLS: s << "https"; break;
      default: s << "http"; break;
    }
    s << " port=" << port;
  }));
#ifndef _WIN32
  if (m_options->eventFD >= 0) {
    uint8_t value = uint8_t(transport);
    (void)::write(m_options->eventFD, &value, 1);
  }
#endif
}

void App::listenFailed(int, bool)
{
  fail_();
}

void App::event_(ZuCSpan event)
{
  if (!m_options->verbose) return;
  ZeString value{event};
  ZiLOG(Info, "zrestd", ([value = ZuMv(value)](auto &s) {
    s << "event=" << value;
  }));
}

void App::fail_()
{
  ++m_errors;
  signal_(Zhttp::ResponseOutcome::BuildFailed);
}

void App::signal_(Zhttp::ResponseOutcome::T outcome)
{
#ifndef _WIN32
  if (m_options->eventFD >= 0) {
    uint8_t value = uint8_t(0x80 | unsigned(outcome));
    (void)::write(m_options->eventFD, &value, 1);
  }
#else
  (void)outcome;
#endif
  done.post();
}

void Parser::init(App &app_)
{
  app = &app_;
  Zrest::MReqParser<Requests>::init(app_);
}


static ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (options.debug) params.debug(true);
  if (options.frag) params.frag(true);
  if (options.yield) params.yield(true);
#else
  (void)options;
#endif
  return params;
}

static void interrupted() { done.post(); }

int main(int argc, char **argv)
{
  ZiHeapCSV::init(::getenv("Z_HEAPTUNE"));
  ZiHashCSV::init(::getenv("Z_HASHTUNE"));

  Options options;
  try {
    if (!loadOptions(options, argc, argv)) usage();
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }

  ZiLog::init("zrestd", options.syslog ? "daemon" : "user");
  ZiLog::level(
#ifdef ZiMultiplex_DEBUG
    options.debug ? Ze::Debug :
#endif
#ifdef Zquic_DEBUG
    options.quicDiag ? Ze::Info :
#endif
    options.memDiag ? Ze::Debug : Ze::Info);
  if (options.syslog)
    ZiLog::sink(ZiLog::sysSink());
  else if (options.logPath && options.logPath != "-")
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.logPath)));
  else
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  if (!prepareProcess(options)) {
    ZiLog::stop();
    return 1;
  }

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zrestd", "ZiMultiplex start failed");
    ZiLog::stop();
    return 1;
  }

  App app{&options};
  if (!app.init()) {
    ZiLOG(Error, "zrestd", "random source initialization failed");
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  Zhttp::Server<App> server;
  server.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zrestd", ([e](auto &s) { s << "transmit error: " << e; }));
    return false;
  }});
  auto config = Zhttp::ServerConfig()
    .localIP(ZiIP(options.addr))
    .port(options.port)
    .idleTimeout(options.timeout)
    .maxConnections(options.maxconn)
    .retainedBodyMax(ReqBodyMax);
  if (options.http) config.tcp();
  if (options.https) {
    Zhttp::H2Policy::T policy;
    switch (options.http2) {
      case Http2Mode::force: policy = Zhttp::H2Policy::Force; break;
      case Http2Mode::disable: policy = Zhttp::H2Policy::Disable; break;
      default: policy = Zhttp::H2Policy::Prefer; break;
    }
    config.tls(Zhttp::H2Config{}
      .certPath(options.cert).keyPath(options.key).policy(policy));
  }
#ifdef Zquic_DEBUG
  bool h3Enabled = false;
#endif
  if (options.http3) {
    double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
    (void)parseDrop(options.quicRxDrop, rxDrop);
    (void)parseDrop(options.quicTxDrop, txDrop);
#endif
    config.quic(Zhttp::QUICConfig{}
      .certPath(options.cert).keyPath(options.key)
      .keyLogPath(options.keyLog)
      .heartbeat(options.quicHeartbeat ?
	ZuTime{options.quicHeartbeat} : ZuTime{})
      .rxDrop(rxDrop).txDrop(txDrop));
#ifdef Zquic_DEBUG
    h3Enabled = true;
#endif
  }
  bool serverInited = server.init(
    Zhttp::HubConfig{&mx, "3", "4"}, ZuMv(config), &app);
  if (!serverInited || !server.start()) {
    ZiLOG(Error, "zrestd", "HTTP server initialization/start failed");
    if (serverInited) (void)server.stop();
    server.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  unsigned memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned quicElapsed = 0;
#endif
  for (;;) {
    unsigned step = options.memDiag ? options.memDiag - memElapsed : 0;
#ifdef Zquic_DEBUG
    if (h3Enabled && options.quicDiag) {
      unsigned left = options.quicDiag - quicElapsed;
      if (!step || left < step) step = left;
    }
#endif
    if (!step) { done.wait(); break; }
    if (!done.timedwait(Zm::now(step))) break;
    if (options.memDiag && (memElapsed += step) >= options.memDiag) {
      memElapsed = 0;
      printMemDiag();
    }
#ifdef Zquic_DEBUG
    if (h3Enabled && options.quicDiag &&
	(quicElapsed += step) >= options.quicDiag) {
      quicElapsed = 0;
      server.printQUICDiag();
    }
#endif
  }
  if (!server.stop()) app.transportFailed();
  server.final();
  mx.stop();
  ZiLog::stop();
  return app.errors() || app.processed() < options.requests ? 1 : 0;
}
